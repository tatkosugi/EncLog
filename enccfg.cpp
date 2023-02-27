// *******************************************************************
// *          Binaly log                                             *
// *                                                                 *
// *              Programed by T.Kosugi    [Jul 17.2019]             *
// *                                                                 *
// * version  date ymd                                               *
// *   0.00  2006.12.26     First release                            *
// *                                                                 *
// *******************************************************************/
//
//		cl /c enccfg.cpp
//		link ftd2xx.lib enccfg.obj

#include <windows.h>

#include <stdio.h> 
#include <stdlib.h> 
#include <stdint.h> 
#include <stdarg.h> 
#include <signal.h> 
#include "ftd2xx.h" 
#include	<time.h>

#define FT_BITMODE_RESET 			0x00
#define FT_BITMODE_ASYNC_BITBANG 	0x01
#define FT_BITMODE_MPSSE 			0x02

#define		DEVICE_LOCAL_ID			0x1444

#define		OPE_WRITE			0x10
#define		OPE_WR_RD			0x31
#define		OPE_WR_RDX			0x34

#define		OPE_SET_DIR			0x80

#define		OPE_SET_CLK_DIV		0x86
#define		OPE_SEND_IMM		0x87

#define		OPE_CLK_DIV_DIS		0x8A	// fast
#define		OPE_CLK_DIV_EN		0x8B	// slow


UCHAR 		Buffer[200];
int			FlagData;
int			TargetAdr;
FILE 		*file;
char		StrBuf[50];

FT_HANDLE	ft; 

int			Sel,Enc;


FT_STATUS write_mpsse(int num, ...) { 
	FT_STATUS ret; 
	va_list args; 
	UCHAR outBuffer[20]; 
	ULONG written; 
	va_start(args, num); 
	for(int i=0; i<num; i++) { 
		outBuffer[i] = va_arg(args, int); 
	} 
	ret = FT_Write(ft, outBuffer, num, &written); 
	va_end(args); 
	return ret; 
} 

 void signal_handler(int signum) { 
	printf("signal detect\n");
 	FT_Close(ft); 
 	exit(0); 
 } 

void	FT_Flash()
{

  	DWORD		dwRxBytes, dwTxBytes, dwEventDWord,dwByteRead;
 	FT_GetStatus(ft, &dwRxBytes, &dwTxBytes, &dwEventDWord);
//	printf("%d \n",dwRxBytes);
 	if(dwRxBytes != 0){
		UCHAR buff[90]; 
		FT_Read(ft, buff, dwRxBytes, &dwByteRead) ;
	}
}

void	FT_Init()
{
 	FT_SetBitMode(ft, 0, FT_BITMODE_RESET); 
 	FT_SetBitMode(ft, (UCHAR)0xFB, FT_BITMODE_MPSSE); // MPSSE, all output 
 	FT_SetLatencyTimer(ft, 16); 
	FT_SetTimeouts(ft, 1000, 1000); 

// 	write_mpsse(1, 0x8A); // disable /5 divider 
// 	write_mpsse(3, 0x86, 0x01, 0x00); // set clock diviser 
 	write_mpsse(1, OPE_CLK_DIV_DIS); // fast
// 	write_mpsse(1, OPE_CLK_DIV_EN); // slow
	write_mpsse(3, OPE_SET_CLK_DIV, 0x02, 0x00); // set clock diviser 
 	write_mpsse(3, OPE_SET_DIR, 0x08, 0x0B); // set pin direction SK, DO, CS for output 
 
}

void	CS_ON()
{
	write_mpsse(3, OPE_SET_DIR, 0x00, 0x0B); // set pin direction SK, DO, CS for output 
}
void	CS_OFF()
{
	write_mpsse(1, OPE_SEND_IMM); // set values immediately 
	write_mpsse(3, OPE_SET_DIR, 0x08, 0x0B); // set pin direction SK, DO, CS for output 
//	write_mpsse(1, OPE_SEND_IMM); // set values immediately 
}

void	RW_SPI_DATA(int len)
{
	ULONG written; 
 	DWORD		dwRxBytes, dwTxBytes, dwEventDWord,dwByteRead;

	CS_ON();
	Buffer[0]	= OPE_WR_RD;
	Buffer[1]	= (len-1) & 0xff;
	Buffer[2]	= (len-1) >> 8;
	FT_Write(ft, Buffer, len+3, &written); 
	CS_OFF();
	
	dwRxBytes = 0;
	while (dwRxBytes !=  len)
	 	FT_GetStatus(ft, &dwRxBytes, &dwTxBytes, &dwEventDWord);

	if(FT_Read(ft, &Buffer[3], dwRxBytes, &dwByteRead) == FT_OK){
	}
}

int		CheckEmpty()
{
	Buffer[3]= 0xa3;
	Buffer[4]= 0;
	Buffer[5]= 0;
//	printf("%2X \n",buffer[4]);
 	RW_SPI_DATA(8);

//	FT_Purge( ft, FT_PURGE_RX | FT_PURGE_TX);

	return	 Buffer[7] & 1;
}


void	PageRead()
{
	Buffer[3]= 0xa1;
	Buffer[4]= TargetAdr >> 8;
	Buffer[5]= TargetAdr & 0xff;
 	RW_SPI_DATA(132);
}

void	FlagSet()
{
	Buffer[3]= 0xa2;
	Buffer[4]= Sel;
	Buffer[5]= Enc;
	Buffer[6]= 0;
	Buffer[7]= 0;
 	RW_SPI_DATA(8);
}

void	IncrementCash()
{
	FlagData	|=	4;
	FlagSet();
	
	TargetAdr	+= 0x80;
	TargetAdr	&= 0x7ff;
}

int		CheckModel()
{
	Buffer[3]= 0xa3;
	Buffer[4]= 0;
	Buffer[5]= 0;
//	printf("%2X \n",buffer[4]);
 	RW_SPI_DATA(15);

//	for (int i=8;i<16;i++){
//		printf("%02X ",Buffer[i]);
//	}
//	printf("\n");
	
//	FT_Purge( ft, FT_PURGE_RX | FT_PURGE_TX);

	return	Buffer[10];
}




int	main(int	argc,char	*argv[])
{ 
	time_t		nowtime;
	int			BlkPage,OpnPort	;

	FT_STATUS					ftStatus;
    DWORD						numDevs;
	FT_DEVICE_LIST_INFO_NODE 	*devInfo;

	ftStatus = FT_CreateDeviceInfoList(&numDevs);
	if(ftStatus == FT_OK){
		//printf("%d\n",numDevs);
		if(numDevs > 0){
			devInfo = new FT_DEVICE_LIST_INFO_NODE[numDevs];
			ftStatus = FT_GetDeviceInfoList(devInfo,&numDevs);
			if (ftStatus == FT_OK){
				for (int i=0; i<(int)numDevs; i++){
					printf("No.%d:\n",i);
					printf("  Flags: 0x%x\n",devInfo[i].Flags);
					printf("  Type: 0x%x\n",devInfo[i].Type);
					printf("  ID: 0x%x\n",devInfo[i].ID);
					printf("  LocId: 0x%x\n",devInfo[i].LocId);
					printf("  SerialNumber: %s\n",devInfo[i].SerialNumber);
					//printf("  Description: %s\n",devInfo[i].Description);
					//printf("  Handle: 0x%x",devInfo[i].ftHandle);
					if (devInfo[i].LocId == DEVICE_LOCAL_ID){
						OpnPort	= i;
						printf("// LocId: 0x%x selected %d\n",devInfo[i].LocId,i);
					}
				}
			}
			delete []devInfo;
		}
	}

	FlagData	= 0;
	TargetAdr	= 0;
	BlkPage		= 10;
//	OpnPort		= 0;

	for (int i = 2 ; i < argc ; i++){
		char		*p;
		p	= argv[i];
		switch (*p++) {
			case 'S' :	// select
				Sel		=  atoi(p);
				break;
			case 'E' :	// encoder type
				Enc		= strtol(p, NULL, 16); 
		}
	}


	time(&nowtime);
	printf("// Start time -  %s",ctime(&nowtime));

 	signal(SIGINT, signal_handler); 
 	if( FT_Open(OpnPort, &ft) != FT_OK ) { 
 		fprintf(stderr, "ss Error\n"); 
 		return 1; 
 	} 
 
	FT_Init();
	FT_Flash();

	if (CheckModel() == 0xd0)
	{
		printf("Config Support Device\n");
		FlagSet();
	};


 	FT_Close(ft); 
 	
	time(&nowtime);
	printf("// END   time -  %s",ctime(&nowtime));

	printf("complete\n");

 	return 0; 
 } 


//	--
//	--	raspberry spi command table
//	--
//	--	A0	registor set	(FLAG set)
//	--	A1	adress preset and read
//	--	A2	rotational period set
//	--	A3	status read
//	--
//	--	CMD		param1		param2		param3
//	--	A0		Flag
//	--	A1		Adr-high	Adr-low		Pading		buffer data
//	--	A2		rot-high	rot-low
//	--	A3		PAD			PAD			PAD			staus 0 .. status 1
//	--


