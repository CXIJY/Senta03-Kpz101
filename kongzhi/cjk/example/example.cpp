// example.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS


#include<WINSOCK2.H>
#include<STDIO.H>
#include<iostream>
#include<cstring>


using namespace std;
#pragma comment(lib, "ws2_32.lib")


int main()
{

	WORD sockVersion = MAKEWORD(2, 2);
	WSADATA data;
	if (WSAStartup(sockVersion, &data) != 0)
	{
		return 0;
	}

	//while (true) 
	{
		//SOCKET sclient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		SOCKET sclient = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sclient == INVALID_SOCKET)
		{
			printf("invalid socket!");
			return 0;
		}

		sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(8060);
		addr.sin_addr.S_un.S_addr = inet_addr("192.168.1.101");
		int addr_len = sizeof(addr);
		if (connect(sclient, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			printf("connect error !");
			closesocket(sclient);
			return 0;
		}

		unsigned char samplerate[4] = { 0xAB,0xC2,0x00,0x03 };		//01 is 1MSPS、03 is 250kSPS, 
		sendto(sclient, (const char*)samplerate, 4, 0, (SOCKADDR*)&addr, sizeof(addr));//int send(int s, const void * msg, int len, unsigned int flags)


		unsigned char trigger[4] = { 0xAB, 0xC6, 0xAA, 0x00 };		//00 is software trigger
		sendto(sclient, (const char*)trigger, 4, 0, (SOCKADDR*)&addr, sizeof(addr));//int send(int s, const void * msg, int len, unsigned int flags)


		unsigned char range[4] = { 0xAB, 0xC3, 0x00, 0xFF };		//FF is ±1V、AA is ±2V、55 is ±5V、00 is ±10V
		sendto(sclient, (const char*)range, 4, 0, (SOCKADDR*)&addr, sizeof(addr));//int send(int s, const void * msg, int len, unsigned int flags)

		unsigned char channel_mask[4] = { 0xAB, 0xC4, 0x00, 0x03};	//01 is CH1、03 is CH1CH2、05 is CH1CH3、09 is CH1CH4、0F is CH1CH2CH3CH4
		sendto(sclient, (const char*)channel_mask, 4, 0, (SOCKADDR*)&addr, sizeof(addr));//int send(int s, const void * msg, int len, unsigned int flags)


		unsigned char start_acquisition[4] = { 0xAB, 0xD1, 0x00, 0xAA };
		sendto(sclient, (const char*)start_acquisition, 4, 0, (SOCKADDR*)&addr, sizeof(addr));//int send(int s, const void * msg, int len, unsigned int flags)


		char recv_buf[1220];
		int recv_packagenumber = recvfrom(sclient, (char*)recv_buf, 1220, 0, (struct sockaddr*)&addr, &addr_len);


		//write to file
		FILE* fp = fopen("SNET03.bin", "wb");
		fwrite(recv_buf, 1220, 1, fp);
		fclose(fp);


		closesocket(sclient);
	}

	WSACleanup();
	std::cout << "finish." << std::endl;
	return 0;

}
