/***********************************************************************
CitcomtFile - Class to encapsulate operations on files generated by the
CITCOMT simulation code.
Copyright (c) 2006-2011 Oliver Kreylos

This file is part of the 3D Data Visualizer (Visualizer).

The 3D Data Visualizer is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 2 of the License, or (at
your option) any later version.

The 3D Data Visualizer is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with the 3D Data Visualizer; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
***********************************************************************/

#include <Concrete/CitcomtFile.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <iomanip>
#include <Misc/ThrowStdErr.h>
#include <Misc/File.h>
#include <Misc/LargeFile.h>
#include <Plugins/FactoryManager.h>
#include <Math/Math.h>
#include <Geometry/Endianness.h>

#include <Concrete/EarthDataSet.h>

namespace Visualization {

namespace Concrete {

/****************************
Methods of class CitcomtFile:
****************************/

CitcomtFile::CitcomtFile(void)
	:BaseModule("CitcomtFile")
	{
	}

Visualization::Abstract::DataSet* CitcomtFile::load(const std::vector<std::string>& args,Cluster::MulticastPipe* pipe) const
	{
	/* Open the data file: */
	Misc::File dataFile(args[0].c_str(),"rt");
	
	/* Check if the user wants to load a specific variable: */
	bool logScale=false;
	const char* varStart=0;
	const char* varEnd=0;
	int varLen=0;
	if(args.size()>1&&args[1][0]!='-')
		{
		/* Parse the search variable name: */
		varStart=args[1].c_str();
		if(strncasecmp(varStart,"log(",4)==0)
			{
			/* Use a logarithmic scale: */
			logScale=true;
			
			/* Take the string in parentheses as search variable name: */
			varStart+=4;
			for(varEnd=varStart;*varEnd!='\0'&&*varEnd!=')';++varEnd)
				;
			}
		else
			{
			/* Take the entire string as search variable name: */
			for(varEnd=varStart;*varEnd!='\0';++varEnd)
				;
			}
		varLen=varEnd-varStart;
		}
	
	/*********************************************************
	Parse any useful information from the CITCOMT file header:
	*********************************************************/
	
	/* Size of data set in C memory / file order: Z varies fastest, then X, then Y: */
	DS::Index numNodes(-1,-1,-1);
	
	/* Ordering of coordinate columns in the file: */
	int coordColumn[3]={-1,-1,-1};
	
	/* Index of data column and name of data variable: */
	int dataColumn=-1;
	char* dataName=0;
	
	/* Mapping from coordinate columns to spherical coordinates (lat, long, rad): */
	int sphericalOrder[3]={-1,-1,-1};
	
	/* Read the first line: */
	char line[256];
	dataFile.gets(line,sizeof(line));
	
	/* Parse the entire header: */
	while(line[0]=='#')
		{
		/* Skip hash marks and whitespace: */
		char* cPtr=line;
		while(*cPtr!='\0'&&(*cPtr=='#'||isspace(*cPtr)))
			++cPtr;
		
		/* Check which header line this is: */
		if(strncasecmp(cPtr,"NODES",5)==0)
			{
			/* Parse the number of nodes: */
			do
				{
				/* Check which dimension this is and parse the number of nodes: */
				if(toupper(cPtr[5])=='Y') // Y column varies most slowly
					numNodes[0]=atoi(cPtr+7);
				else if(toupper(cPtr[5])=='X')
					numNodes[1]=atoi(cPtr+7);
				if(toupper(cPtr[5])=='Z') // Z column varies fastest
					numNodes[2]=atoi(cPtr+7);
				
				/* Go to the next field: */
				while(*cPtr!='\0'&&!isspace(*cPtr))
					++cPtr;
				while(*cPtr!='\0'&&isspace(*cPtr))
					++cPtr;
				}
			while(strncasecmp(cPtr,"NODES",5)==0);
			}
		else if((toupper(cPtr[0])=='X'||toupper(cPtr[0])=='Y'||toupper(cPtr[0])=='Z')&&cPtr[1]=='-')
			{
			/* Parse spherical coordinate assignments: */
			do
				{
				/* Remember which coordinate this is: */
				int coordIndex=int(toupper(cPtr[0]))-int('X');
				
				/* Find the end of the spherical component string: */
				char* endPtr;
				for(endPtr=cPtr;*endPtr!='\0'&&*endPtr!=','&&!isspace(*endPtr);++endPtr)
					;
				
				/* Check which component is assigned: */
				if(endPtr-cPtr==5&&strncasecmp(cPtr+2,"LAT",3)==0)
					sphericalOrder[0]=coordIndex;
				else if(endPtr-cPtr==5&&strncasecmp(cPtr+2,"LON",3)==0)
					sphericalOrder[1]=coordIndex;
				if(endPtr-cPtr==8&&strncasecmp(cPtr+2,"RADIUS",3)==0)
					sphericalOrder[2]=coordIndex;
				
				/* Go to the next field: */
				cPtr=endPtr;
				if(*cPtr==',')
					++cPtr;
				while(*cPtr!='\0'&&isspace(*cPtr))
					++cPtr;
				}
			while((toupper(cPtr[0])=='X'||toupper(cPtr[0])=='Y'||toupper(cPtr[0])=='Z')&&cPtr[1]=='-');
			}
		else if(*cPtr=='|')
			{
			/* Parse column assignments: */
			int columnIndex=0;
			do
				{
				/* Skip separator and whitespace: */
				while(*cPtr!='\0'&&(*cPtr=='|'||isspace(*cPtr)))
					++cPtr;
				
				/* Find the end of the column string: */
				char* endPtr;
				for(endPtr=cPtr;*endPtr!='\0'&&!isspace(*endPtr);++endPtr)
					;
				
				/* Check which column this is: */
				if(endPtr-cPtr==1&&strncasecmp(cPtr,"X",1)==0)
					coordColumn[0]=columnIndex;
				else if(endPtr-cPtr==1&&strncasecmp(cPtr,"Y",1)==0)
					coordColumn[1]=columnIndex;
				else if(endPtr-cPtr==1&&strncasecmp(cPtr,"Z",1)==0)
					coordColumn[2]=columnIndex;
				else if(endPtr-cPtr==4&&strncasecmp(cPtr,"NODE",4)==0)
					{
					/* Ignore this column */
					}
				else if(dataColumn<0)
					{
					if(varStart==0||(endPtr-cPtr==varLen&&strncasecmp(varStart,cPtr,varLen)==0))
						{
						/* Treat this column as the data column: */
						dataColumn=columnIndex;

						/* Remember the name of the data value: */
						if(logScale)
							{
							dataName=new char[varLen+6];
							memcpy(dataName,"Log(",4);
							memcpy(dataName+4,cPtr,varLen);
							dataName[4+varLen]=')';
							dataName[4+varLen+1]='\0';
							}
						else
							{
							dataName=new char[varLen+1];
							memcpy(dataName,cPtr,varLen);
							dataName[varLen]='\0';
							}
						}
					}
				
				/* Go to the next column: */
				cPtr=endPtr;
				while(*cPtr!='\0'&&isspace(*cPtr))
					++cPtr;
				++columnIndex;
				}
			while(*cPtr=='|');
			}
		
		/* Go to the next line: */
		dataFile.gets(line,sizeof(line));
		}
	
	/* Check if all required header information has been read: */
	bool headerValid=true;
	for(int i=0;i<3;++i)
		if(numNodes[i]<0)
			headerValid=false;
	for(int i=0;i<3;++i)
		if(coordColumn[i]<0)
			headerValid=false;
	if(dataColumn<0)
		{
		if(varStart!=0)
			Misc::throwStdErr("CitcomtFile::load: Data variable %s not found in CITCOMT header in input file %s",args[1].c_str(),args[0].c_str());
		headerValid=false;
		}
	if(!headerValid)
		Misc::throwStdErr("CitcomtFile::load: Invalid CITCOMT header in input file %s",args[0].c_str());
	
	/* Create result data set: */
	EarthDataSet<DataSet>* result=new EarthDataSet<DataSet>(args);
	result->getDs().setData(numNodes);
	
	/* Set the data value's name: */
	result->getDataValue().setScalarVariableName(dataName);
	
	/* Check if the file is stored in spherical coordinates: */
	bool sphericalCoordinates=true;
	for(int i=0;i<3;++i)
		if(sphericalOrder[i]<0)
			sphericalCoordinates=false;
	
	/* Constant parameters for geoid formula: */
	const double a=6378.14e3; // Equatorial radius in m
	const double f=1.0/298.247; // Geoid flattening factor
	const double scaleFactor=1.0e-3; // Scale factor for Cartesian coordinates
	
	/* Determine the number of significant columns: */
	int numColumns=dataColumn;
	for(int i=0;i<3;++i)
		if(numColumns<coordColumn[i])
			numColumns=coordColumn[i];
	++numColumns;
	
	/* Compute a mapping from column indices to coordinate components/data value: */
	int* columnMapping=new int[numColumns];
	for(int i=0;i<numColumns;++i)
		columnMapping[i]=-1;
	for(int i=0;i<3;++i)
		columnMapping[coordColumn[i]]=i;
	columnMapping[dataColumn]=3;
	
	/* Read all vertex positions and values: */
	std::cout<<"Reading grid vertex positions and values...   0%"<<std::flush;
	DS::Array& vertices=result->getDs().getVertices();
	unsigned int vertexCounter=1;
	unsigned int nextTick=numNodes[1]*numNodes[2];
	unsigned int totalNumNodes=numNodes[0]*numNodes[1]*numNodes[2];
	for(DS::Array::iterator vIt=vertices.begin();vIt!=vertices.end();++vIt,++vertexCounter)
		{
		/* Parse the coordinate components and the data value from the line: */
		double columns[4];
		char* cPtr=line;
		for(int i=0;i<numColumns;++i)
			{
			/* Read the column value: */
			double val=strtod(cPtr,&cPtr);
			if(columnMapping[i]>=0)
				columns[columnMapping[i]]=val;
			}
		
		if(sphericalCoordinates)
			{
			/* Convert from spherical to Cartesian coordinates: */
			double latitude=columns[sphericalOrder[0]];
			double longitude=columns[sphericalOrder[1]];
			double radius=columns[sphericalOrder[2]];
			double s0=Math::sin(latitude);
			double c0=Math::cos(latitude);
			double r=(a*(1.0-f*Math::sqr(s0))*radius)*scaleFactor;
			double xy=r*c0;
			double s1=Math::sin(longitude);
			double c1=Math::cos(longitude);
			vIt->pos[0]=float(xy*c1);
			vIt->pos[1]=float(xy*s1);
			vIt->pos[2]=float(r*s0);
			}
		else
			{
			/* Store the vertex position: */
			for(int i=0;i<3;++i)
				vIt->pos[i]=float(columns[i]);
			}
		
		/* Store the vertex value: */
		if(logScale)
			vIt->value=float(Math::log10(columns[3]));
		else
			vIt->value=float(columns[3]);
		
		/* Read the next line from the file: */
		dataFile.gets(line,sizeof(line));
		
		if(vertexCounter==nextTick)
			{
			std::cout<<"\b\b\b\b"<<std::setw(3)<<(vertexCounter*100)/totalNumNodes<<"%"<<std::flush;
			nextTick+=numNodes[1]*numNodes[2];
			}
		}
	std::cout<<"\b\b\b\bdone"<<std::endl;
	
	/* Clean up: */
	delete[] dataName;
	delete[] columnMapping;
	
	/* Finalize the grid structure: */
	std::cout<<"Finalizing grid structure..."<<std::flush;
	result->getDs().finalizeGrid();
	std::cout<<" done"<<std::endl;
	
	#if 0
	/* Save the data set as a signed distance function: */
	Misc::LargeFile volFile("/work/okreylos/GriddedData/ExpData/slab2.fvol","wb",Misc::LargeFile::BigEndian);
	volFile.write<int>(numNodes.getComponents(),3);
	volFile.write<int>(0);
	float domainSize[3];
	for(int i=0;i<3;++i)
		domainSize[i]=float(numNodes[i]-1);
	volFile.write<float>(domainSize,3);
	for(DS::Array::iterator vIt=vertices.begin();vIt!=vertices.end();++vIt)
		{
		float val=(float(vIt->value)-3.5f)*10.0f+0.5f;
		if(val<0.0f)
			val=0.0f;
		else if(val>1.0f)
			val=1.0f;
		volFile.write<float>(val);
		}
	#endif
	
	return result;
	}

Visualization::Abstract::DataSetRenderer* CitcomtFile::getRenderer(const Visualization::Abstract::DataSet* dataSet) const
	{
	return new EarthDataSetRenderer<DataSet,DataSetRenderer>(dataSet);
	}

int CitcomtFile::getNumScalarAlgorithms(void) const
	{
	return BaseModule::getNumScalarAlgorithms();
	}

int CitcomtFile::getNumVectorAlgorithms(void) const
	{
	return 0;
	}

}

}

/***************************
Plug-in interface functions:
***************************/

extern "C" Visualization::Abstract::Module* createFactory(Plugins::FactoryManager<Visualization::Abstract::Module>& manager)
	{
	/* Create module object and insert it into class hierarchy: */
	Visualization::Concrete::CitcomtFile* module=new Visualization::Concrete::CitcomtFile();
	
	/* Return module object: */
	return module;
	}

extern "C" void destroyFactory(Visualization::Abstract::Module* module)
	{
	delete module;
	}