#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "bass.h"
#include <vector>
#include <string>
#include <math.h>
#include <malloc.h>
#include <ShlObj.h>
#pragma comment (lib, "bass.lib")


#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")


HWND win=NULL;
#define SPECWIDTH 368	// width
#define SPECHEIGHT 127	// height
HDC specdc=0;
HBITMAP specbmp=0;
BYTE *specbuf;
int specmode=0,specpos=0; // spectrum mode

using namespace std;
string _FN_= "";
bool _PLAY_ = false;
bool _PAUSE_ = false;
int _TRACK_ = -1;
bool _RADIO_ = false;
int _count_search_ = -1;
bool _CH_ = true;
vector <string> File_Name;
HSTREAM can = NULL;

HFX fx[4];
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
DWORD floatable;
HDC dc;

// display error messages
void Error(const char *es)
{
	char mes[200];
	sprintf_s(mes,"%s\n(error code: %d)",es,BASS_ErrorGetCode());
	MessageBox(win,LPSTR(mes),0,0);
}

// messaging macros
#define MESS(id,m,w,l) SendDlgItemMessage(win,id,m,(WPARAM)(w),(LPARAM)(l))
#define STLM(m,w,l) MESS(10,m,w,l)
#define MLM(m,w,l) MESS(20,m,w,l)
#define SLM(m,w,l) MESS(30,m,w,l)
#define GETSTR() STLM(LB_GETCURSEL,0,0)
#define GETMOD() MLM(LB_GETCURSEL,0,0)
#define GETSAM() SLM(LB_GETCURSEL,0,0)

// "rotate"
HDSP rotdsp=0;	// DSP handle
float rotpos;	// cur.pos

void CALLBACK Rotate(HDSP handle, DWORD channel, void *buffer, DWORD length, void *user)
{
	float *d=(float*)buffer;
	DWORD a;

	for (a=0;a<length/4;a+=2) 
	{
		d[a]*=fabs(sin(rotpos));
		d[a+1]*=fabs(cos(rotpos));
		rotpos+=0.00003;
	}
	rotpos=fmod(float(rotpos),float(2*M_PI));
}

// "echo"
HDSP echdsp=0;	// DSP handle
#define ECHBUFLEN 1200	// buffer length
float echbuf[ECHBUFLEN][2];	// buffer
int echpos;	// cur.pos

void CALLBACK Echo(HDSP handle, DWORD channel, void *buffer, DWORD length, void *user)
{
	float *d=(float*)buffer;
	DWORD a;

	for (a=0;a<length/4;a+=2) 
	{
		float l=d[a]+(echbuf[echpos][1]/2);
		float r=d[a+1]+(echbuf[echpos][0]/2);
#if 1 // 0=echo, 1=basic "bathroom" reverb
		echbuf[echpos][0]=d[a]=l;
		echbuf[echpos][1]=d[a+1]=r;
#else
		echbuf[echpos][0]=d[a];
		echbuf[echpos][1]=d[a+1];
		d[a]=l;
		d[a+1]=r;
#endif
		echpos++;
		if (echpos==ECHBUFLEN) echpos=0;
	}
}

// "flanger"
HDSP fladsp=0;	// DSP handle
#define FLABUFLEN 350	// buffer length
float flabuf[FLABUFLEN][2];	// buffer
int flapos;	// cur.pos
float flas,flasinc;	// sweep pos/increment

void CALLBACK Flange(HDSP handle, DWORD channel, void *buffer, DWORD length, void *user)
{
	float *d=(float*)buffer;
	DWORD a;

	for (a=0;a<length/4;a+=2) 
	{
		int p1=(flapos+(int)flas)%FLABUFLEN;
		int p2=(p1+1)%FLABUFLEN;
		float f=flas-(int)flas;
		float s;

		s=(d[a]+((flabuf[p1][0]*(1-f))+(flabuf[p2][0]*f)))*0.7;
		flabuf[flapos][0]=d[a];
		d[a]=s;

		s=(d[a+1]+((flabuf[p1][1]*(1-f))+(flabuf[p2][1]*f)))*0.7;
		flabuf[flapos][1]=d[a+1];
		d[a+1]=s;

		flapos++;
		if (flapos==FLABUFLEN) flapos=0;
		flas+=flasinc;
		if (flas<0 || flas>FLABUFLEN-1) 
		{
			flasinc=-flasinc;
			flas+=flasinc;
		}
	}
}


void UpdateFX(int b, HWND h)
{
	int v=SendMessage(GetDlgItem(h, 50+b),TBM_GETPOS,0,0);
	if (b<3) 
	{
		BASS_DX8_PARAMEQ p;
		BASS_FXGetParameters(fx[b],&p);
		p.fGain=10.0-v;
		BASS_FXSetParameters(fx[b],&p);
		char tmp[4];
		sprintf(tmp,"%u",abs(v-10));
		if (v > 10)
		{
			string ttmp="-";
			ttmp+=tmp;
			SendMessage(GetDlgItem(h, 60+b),WM_SETTEXT,0,(LPARAM)ttmp.c_str());
		}else
			SendMessage(GetDlgItem(h, 60+b),WM_SETTEXT,0,(LPARAM)tmp);
	} else {
		BASS_DX8_REVERB p;
		BASS_FXGetParameters(fx[3],&p);
		p.fReverbMix=(v<20?log(1-v/20.0)*20:-96);
		BASS_FXSetParameters(fx[3],&p);
		char tmp[4];
		sprintf(tmp,"%u",abs(v-20));
		SendMessage(GetDlgItem(h, 60+b),WM_SETTEXT,0,(LPARAM)tmp);
	}
}

bool Play_Chan(HWND h)
{
	if (MESS(25,BM_GETCHECK,0,0)) 
	{
		rotpos=0.7853981f;
		rotdsp=BASS_ChannelSetDSP(can,&Rotate,0,2);
	} 
	if (MESS(26,BM_GETCHECK,0,0)) 
	{
		memset(echbuf,0,sizeof(echbuf));
		echpos=0;
		echdsp=BASS_ChannelSetDSP(can,&Echo,0,1);
	}
	if (MESS(27,BM_GETCHECK,0,0)) 
	{
		memset(flabuf,0,sizeof(flabuf));
		flapos=0;
		flas=FLABUFLEN/2;
		flasinc=0.002f;
		fladsp=BASS_ChannelSetDSP(can,&Flange,0,0);
	}
		BASS_DX8_PARAMEQ p;
		fx[0]=BASS_ChannelSetFX(can,BASS_FX_DX8_PARAMEQ,0);
		fx[1]=BASS_ChannelSetFX(can,BASS_FX_DX8_PARAMEQ,0);
		fx[2]=BASS_ChannelSetFX(can,BASS_FX_DX8_PARAMEQ,0);
		fx[3]=BASS_ChannelSetFX(can,BASS_FX_DX8_REVERB,0);
		p.fGain=0;
		p.fBandwidth=18;
		p.fCenter=125;
		BASS_FXSetParameters(fx[0],&p);
		p.fCenter=1000;
		BASS_FXSetParameters(fx[1],&p);
		p.fCenter=8000;
		BASS_FXSetParameters(fx[2],&p);
		UpdateFX(0,h);
		UpdateFX(1,h);
		UpdateFX(2,h);
		UpdateFX(3,h);

		return BASS_ChannelPlay(can,FALSE);
}

void Buttom_Play(HWND h)
{
	if (!_PLAY_)
	{
		int s=GETSTR();
		if (s!=LB_ERR)
		{
			can = BASS_StreamCreateFile(FALSE,File_Name[s].c_str(),0,0,BASS_SAMPLE_FX|floatable);
			if (!Play_Chan(h))
				Error("Can't play stream");
			else
			{
				_PLAY_ = true;
				_PAUSE_ = false;
				_RADIO_ = false;
				_TRACK_ = s;
				QWORD bytes=BASS_ChannelGetLength(can,BASS_POS_BYTE);
				DWORD time=BASS_ChannelBytes2Seconds(can,bytes);
				MESS(17,TBM_SETRANGE,1,MAKELONG(0,time));
				_FN_="";
			}
		}
	}else{
		if (!_PAUSE_)
		{
			BASS_ChannelStop(can); // play the stream from the start
			BASS_StreamFree(can);
			int s=GETSTR();
			if (s!=LB_ERR)
			{
				can = BASS_StreamCreateFile(FALSE,File_Name[s].c_str(),0,0,BASS_SAMPLE_FX|floatable);
				if (!Play_Chan(h))
					Error("Can't play stream");
				else
				{
					_PLAY_ = true;
					_RADIO_ = false;
					_TRACK_ = s;
					QWORD bytes=BASS_ChannelGetLength(can,BASS_POS_BYTE);
					DWORD time=BASS_ChannelBytes2Seconds(can,bytes);
					MESS(17,TBM_SETRANGE,1,MAKELONG(0,time));
					_FN_="";
				}
			}
		}else{
			if (GETSTR() == _TRACK_)
			{
				if (!BASS_ChannelPlay(can,FALSE))
					Error("Can't play stream");
				_PAUSE_ = false;
			}else{
				_PAUSE_=false;
				BASS_ChannelStop(can); // play the stream from the start
				BASS_StreamFree(can);
				int s=GETSTR();
				if (s!=LB_ERR)
				{
					can = BASS_StreamCreateFile(FALSE,File_Name[s].c_str(),0,0,BASS_SAMPLE_FX|floatable);
					if (!Play_Chan(h))
						Error("Can't play stream");
					else
					{
						_PLAY_ = true;
						_RADIO_ = false;
						_TRACK_ = s;
						QWORD bytes=BASS_ChannelGetLength(can,BASS_POS_BYTE);
						DWORD time=BASS_ChannelBytes2Seconds(can,bytes);
						MESS(17,TBM_SETRANGE,1,MAKELONG(0,time));
						_FN_="";
					}
				}
			}
		}
	}
}

BOOL CALLBACK dialogproc(HWND h,UINT m,WPARAM w,LPARAM l)
{
	static OPENFILENAME ofn;
	win = h;
	switch (m) 
	{
		case WM_PAINT:
		{ // create bitmap to draw spectrum in (8 bit for easy updating)
			win=h;
			BYTE data[2000]={0};
			BITMAPINFOHEADER *bh=(BITMAPINFOHEADER*)data;
			RGBQUAD *pal=(RGBQUAD*)(data+sizeof(*bh));
			int a;
			bh->biSize=sizeof(*bh);
			bh->biWidth=SPECWIDTH;
			bh->biHeight=SPECHEIGHT; // upside down (line 0=bottom)
			bh->biPlanes=1;
			bh->biBitCount=8;
			bh->biClrUsed=bh->biClrImportant=256;
			// setup palette
			for (a=1;a<128;a++) 
			{
				pal[a].rgbGreen=256-2*a;
				pal[a].rgbRed=2*a;
			}
			for (a=0;a<32;a++) 
			{
				pal[128+a].rgbBlue=8*a;
				pal[128+32+a].rgbBlue=255;
				pal[128+32+a].rgbRed=8*a;
				pal[128+64+a].rgbRed=255;
				pal[128+64+a].rgbBlue=8*(31-a);
				pal[128+64+a].rgbGreen=8*a;
				pal[128+96+a].rgbRed=255;
				pal[128+96+a].rgbGreen=255;
				pal[128+96+a].rgbBlue=8*a;
			}
			// create the bitmap
			specbmp=CreateDIBSection(0,(BITMAPINFO*)bh,DIB_RGB_COLORS,(void**)&specbuf,NULL,0);
			specdc=CreateCompatibleDC(0);
			SelectObject(specdc,specbmp);
			if (GetUpdateRect(h,0,0)) 
			{
				PAINTSTRUCT p;
				if (!(dc=BeginPaint(h,&p))) return 0;
				BitBlt(dc,350,10,SPECWIDTH,SPECHEIGHT,specdc,0,0,SRCCOPY);
			}
		}
		return 0;
		case WM_LBUTTONUP:
		{
			int x=0,y=0;
			x = LOWORD(l);
			y = HIWORD(l);
			if (350<x && x<350+SPECWIDTH && 10<y && y<10+SPECHEIGHT)
			{
				specmode=(specmode+1)%4; // swap spectrum mode
				if (specbuf != NULL) 
					memset(specbuf,0,SPECWIDTH*SPECHEIGHT);	// clear display
			}
		}
		return 0;
		case WM_TIMER:
		{
			switch (LOWORD(w)) 
			{
			case 1:
			{	
				if (_PLAY_ && !_RADIO_)
				{
					QWORD bytes=BASS_ChannelGetPosition(can,BASS_POS_BYTE);
					int time=BASS_ChannelBytes2Seconds(can,bytes);
					MESS(17,TBM_SETPOS,1,(DWORD)time);
					if (BASS_ChannelIsActive(can)==BASS_ACTIVE_STOPPED)
					{
						BASS_ChannelStop(can); // play the stream from the start
						BASS_StreamFree(can);
						int s=GETSTR();
						++s;
						if (s < SendMessage(GetDlgItem(h, 10) ,LB_GETCOUNT,0,0))
						{
							SendMessage(GetDlgItem(h, 10), LB_SETCURSEL, (WPARAM)s, 0 );
							if (s!=LB_ERR)
							{
								can = BASS_StreamCreateFile(FALSE,File_Name[s].c_str(),0,0,BASS_SAMPLE_FX|floatable);
								if (!BASS_ChannelPlay(can,FALSE))
									Error("Can't play stream");
								else
								{
									_PLAY_ = true;
									_TRACK_ = s;
									QWORD bytes=BASS_ChannelGetLength(can,BASS_POS_BYTE);
									DWORD time=BASS_ChannelBytes2Seconds(can,bytes);
									MESS(17,TBM_SETRANGE,1,MAKELONG(0,time));
									_FN_="";
								}
							}
						}else{
							BASS_ChannelStop(can); // play the stream from the start
							BASS_StreamFree(can);
							_PLAY_ = false;
							_PAUSE_ = false;
							SendMessage(GetDlgItem(h, 10), LB_SETCURSEL, (WPARAM)-1, 0 );
							_TRACK_ = -1;
							MESS(17,TBM_SETPOS,1,(DWORD)0);
					
						}
					}
					char tmp[128];
					if (_PLAY_) 
						sprintf(tmp,"%u:%02u",time/60,time%60);
					else
						sprintf(tmp,"%u:%02u",0,0);
					MESS(19,WM_SETTEXT,0,tmp);
				}
//--------------------------------------------------------------------------------------
if (BASS_ChannelIsActive(can)!=BASS_ACTIVE_STOPPED && _PLAY_ && !_PAUSE_)
{
	HDC dc;
	win = h;
	long long x,y,y1;
	if (specmode==3) 
	{ // waveform
		int c;
		float *buf;
		BASS_CHANNELINFO ci;
		memset(specbuf,0,SPECWIDTH*SPECHEIGHT);
		BASS_ChannelGetInfo(can,&ci); // get number of channels
		buf=(float*)alloca(ci.chans*SPECWIDTH*sizeof(float)); // allocate buffer for data
		BASS_ChannelGetData(can,buf,(ci.chans*SPECWIDTH*sizeof(float))|BASS_DATA_FLOAT); // get the sample data (floating-point to avoid 8 & 16 bit processing)
		for (c=0;c<ci.chans;c++) 
		{
			for (x=0;x<SPECWIDTH;x++) 
			{
				int v=(1-buf[x*ci.chans+c])*SPECHEIGHT/2; // invert and scale to fit display
				if (v<0) v=0;
				else if (v>=SPECHEIGHT) v=SPECHEIGHT-1;
				if (!x) y=v;
				do { // draw line from previous sample...
					if (y<v) y++;
					else if (y>v) y--;
					specbuf[y*SPECWIDTH+x]=c&1?127:1; // left=green, right=red (could add more colours to palette for more chans)
				} while (y!=v);
			}
		}
	}else{
		float fft[1024];
		BASS_ChannelGetData(can,fft,BASS_DATA_FFT2048); // get the FFT data
		if (!specmode) 
		{ // "normal" FFT
			memset(specbuf,0,SPECWIDTH*SPECHEIGHT);
			for (x=0;x<SPECWIDTH/2;x++) 
			{
#if 1
				y=sqrt(fft[x+1])*3*SPECHEIGHT-4; // scale it (sqrt to make low values more visible)
#else
				y=fft[x+1]*10*SPECHEIGHT; // scale it (linearly)
#endif
				if (y>SPECHEIGHT) 
					y=SPECHEIGHT; // cap it
				if (x && (y1=(y+y1)/2)) // interpolate from previous to make the display smoother
					while (--y1>=0) 
						specbuf[y1*SPECWIDTH+x*2-1]=y1+1;
				y1=y;
				while (--y>=0) 
					specbuf[y*SPECWIDTH+x*2]=y+1; // draw level
			}
		} else if (specmode==1) { // logarithmic, acumulate & average bins
			int b0=0;
			memset(specbuf,0,SPECWIDTH*SPECHEIGHT);
#define BANDS 28
			for (x=0;x<BANDS;x++) 
			{
				float peak=0;
				int b1=pow(2,x*10.0/(BANDS-1));
				if (b1>1023) b1=1023;
				if (b1<=b0) b1=b0+1; // make sure it uses at least 1 FFT bin
				for (;b0<b1;b0++)
					if (peak<fft[1+b0]) 
						peak=fft[1+b0];
				y=sqrt(peak)*3*SPECHEIGHT-4; // scale it (sqrt to make low values more visible)
				if (y>SPECHEIGHT) y=SPECHEIGHT; // cap it
				while (--y>=0)
					memset(specbuf+y*SPECWIDTH+x*(SPECWIDTH/BANDS),y+1,SPECWIDTH/BANDS-2); // draw bar
			}
		} else { // "3D"
			for (x=0;x<SPECHEIGHT;x++) 
			{
				y=sqrt(fft[x+1])*3*127; // scale it (sqrt to make low values more visible)
				if (y>127) 
					y=127; // cap it
				specbuf[x*SPECWIDTH+specpos]=128+y; // plot it
			}
			// move marker onto next position
			specpos=(specpos+1)%SPECWIDTH;
			for (x=0;x<SPECHEIGHT;x++) 
				specbuf[x*SPECWIDTH+specpos]=255;
		}
	}
	// update the display
	dc=GetDC(win);
	BitBlt(dc,350,10,SPECWIDTH,SPECHEIGHT,specdc,0,0,SRCCOPY);
	ReleaseDC(win,dc);

}
///--------------------------------------------------------------------------------------
			}
			break;
			case 100:
			{
				if (_PLAY_ && !_RADIO_)
				{
					if (_FN_.length()==0)
					{
						char *tmp_=new char[256];                 
						SendMessage(GetDlgItem(h, 10), LB_GETTEXT, (WPARAM)_TRACK_, (LPARAM)tmp_ );
						_FN_ = tmp_;
						delete [] tmp_;
					}else
						_FN_.erase(_FN_.begin());
					SetWindowText(h,_FN_.c_str());
				}else
					SetWindowText(h,"V!nt Player");
					if (_RADIO_)
					{
						const char *icy=BASS_ChannelGetTags(can,BASS_TAG_ICY);
						if (!icy) icy=BASS_ChannelGetTags(can,BASS_TAG_HTTP); // no ICY tags, try HTTP
						if (icy) 
						{
							for (;*icy;icy+=strlen(icy)+1) 
							{
								if (!strnicmp(icy,"icy-name:",9))
									SetWindowText(h,icy+9);
								if (!strnicmp(icy,"icy-url:",8))
									SetWindowText(h,icy+8);
							}
						} else
							SetWindowText(h,"V!nt Player");
					}
			}
			break;
			case 50:
			{ // monitor prebuffering progress
				DWORD progress=BASS_StreamGetFilePosition(can,BASS_FILEPOS_BUFFER)*100/BASS_StreamGetFilePosition(can,BASS_FILEPOS_END); // percentage of buffer filled
				if (progress>75 || !BASS_StreamGetFilePosition(can,BASS_FILEPOS_CONNECTED)) 
				{ // over 75% full (or end of download)
					KillTimer(win,0); // finished prebuffering, stop monitoring
					// get the stream title and set sync for subsequent titles
					BASS_ChannelSetSync(can,BASS_SYNC_META,0,0,0); // Shoutcast
					BASS_ChannelSetSync(can,BASS_SYNC_OGG_CHANGE,0,0,0); // Icecast/OGG
					// set sync for end of stream
					BASS_ChannelSetSync(can,BASS_SYNC_END,0,0,0);
					// play it!
					BASS_ChannelPlay(can,FALSE);
				} 
			}
			break;
			}
		}
		break;
		case WM_COMMAND:
			switch (LOWORD(w)) 
			{
				case IDCANCEL:
					DestroyWindow(h);
				break;
				case 14:
				{
					_CH_ = true;
					char file[MAX_PATH]="";
					HSTREAM str;
					ofn.lpstrFilter=LPSTR("Streamable files (mp3)\0*.mp3\0All files\0*.*\0\0");
					ofn.lpstrFile=LPSTR(file);
					if (GetOpenFileName(&ofn)) 
					{
						if (str=BASS_StreamCreateFile(FALSE,file,0,0,0)) 
						{
							File_Name.push_back(file);
							TAG_ID3 *id3=(TAG_ID3*)BASS_ChannelGetTags(str,BASS_TAG_ID3);
							string tmp2;
							if (id3)
							{
								tmp2 = id3->artist;
								tmp2 +=" ";
								tmp2 += id3->title;
							}else{
								tmp2 = (strrchr(file,'\\')+1);
								tmp2.erase((tmp2.length()-4),4);
							}
							STLM(LB_ADDSTRING,0,tmp2.c_str());
							BASS_StreamFree(str);
						} else 
							Error("Can't open stream");
					}
				}
				break;
				case 15:
				{
					int s=GETSTR();
					if (s!=LB_ERR) 
					{
						STLM(LB_DELETESTRING,s,0);
						File_Name.erase(File_Name.begin() + s);
						_CH_ = true;
					}
				}
				break;
				case 11:
				{
					Buttom_Play(h);
				}
				break;
				case 12:
				{
					if (!_PAUSE_)
						BASS_ChannelPause(can); // stop the stream
					else
						BASS_ChannelPlay(can,FALSE);
					_PAUSE_ = !_PAUSE_;
				}
				break;
				case 13:
				{
					if(_PLAY_)
					{
						BASS_ChannelStop(can); // play the stream from the start
						BASS_StreamFree(can);
						_PLAY_ = false;
						_PAUSE_=false;
						_RADIO_=false;
						MESS(17,TBM_SETRANGE,1,0);
					}
				}
				break;
				case 18:
				{
					SendMessage(GetDlgItem(h, 29), PBM_SETRANGE, 0, (LPARAM)MAKELONG(0,150));
					SendMessage(GetDlgItem(h, 29), PBM_SETSTEP, (WPARAM)1, 0);
					SendMessage(GetDlgItem(h, 29), PBM_SETPOS, (WPARAM)0, 0);
					UpdateWindow(GetDlgItem(h, 29));
					char buf[1024]; 
					BROWSEINFO bi; 
					bi.hwndOwner = h; 
					bi.pidlRoot=NULL; 
					bi.pszDisplayName=buf; 
					bi.lpszTitle="¬ыберите директорию, а то руки оторву!"; 
					bi.ulFlags=NULL; 
					bi.lpfn=NULL; 
					ITEMIDLIST *itls; 
					if((itls=SHBrowseForFolder(&bi)) != NULL) 
						SHGetPathFromIDList(itls,buf); 
					WIN32_FIND_DATA FindFileData;
					HANDLE hf;
					string tmp = buf;
					tmp += "\\*.mp3";
					hf = FindFirstFile(tmp.c_str(),&FindFileData);
					if(hf != INVALID_HANDLE_VALUE)
					{
						int i = 0;
						do{
							SendMessage(GetDlgItem(h, 29), PBM_STEPIT, 0, 0);
							UpdateWindow(GetDlgItem(h, 29));
							string file = buf;
							file += "\\";
							file += FindFileData.cFileName;
							HSTREAM str;
							if (str=BASS_StreamCreateFile(FALSE,file.c_str(),0,0,0)) 
							{
								File_Name.push_back(file);
								TAG_ID3 *id3=(TAG_ID3*)BASS_ChannelGetTags(str,BASS_TAG_ID3);
								string tmp2;
								if (id3){	
									tmp2 = id3->artist;
									tmp2 +=" ";
									tmp2 += id3->title;
								}else{	
									tmp2 = FindFileData.cFileName;
									tmp2.erase((tmp2.length()-4),4);
								}
								STLM(LB_ADDSTRING,0,tmp2.c_str());
								BASS_StreamFree(str);
							}else{
								string tmp2;
								tmp2 = FindFileData.cFileName;
								tmp2.erase((tmp2.length()-4),4);
								STLM(LB_ADDSTRING,0,tmp2.c_str());
								File_Name.push_back(file);
							}		
						}while(FindNextFile(hf,&FindFileData) != 0);
						FindClose(hf);
					}
					SendMessage(GetDlgItem(h, 29), PBM_SETPOS, (WPARAM)0, 0);
					UpdateWindow(GetDlgItem(h, 29));
				}
				break;
				case 21:
				{
					char tmp_[256];                 
					MESS(20,WM_GETTEXT,sizeof(tmp_),tmp_);
					int count_str = SendMessage(GetDlgItem(h, 10) ,LB_GETCOUNT,0,0);
					bool P = false;
					int i = 0;
					if (!_CH_) 
						i = _count_search_+1;
					else
						_CH_=false;
					do{
						char *tmp_2=new char[256];                 
						SendMessage(GetDlgItem(h, 10), LB_GETTEXT, (WPARAM)i, (LPARAM)tmp_2 );
						if (strstr(tmp_2, tmp_))
						{
							_count_search_=i;
							SendMessage(GetDlgItem(h, 10), LB_SETCURSEL, (WPARAM)i, 0 );
							P=true;
						}
						++i;
						delete [] tmp_2;
					}while(i < count_str && !P);
					if (!P) 
					{
						_count_search_ = -1;
						SendMessage(GetDlgItem(h, 10), LB_SETCURSEL, (WPARAM)-1, 0 );
					}
				}
				break;
				case 20:
				{
					_CH_ = true;
				}
				break;
				case 23:
				{
					char *url;
					char temp[200];
					MESS(17,TBM_SETRANGE,1,0);
					MESS(22,WM_GETTEXT,sizeof(temp),temp);
					url=strdup(temp);
					BASS_StreamFree(can);
					can= BASS_StreamCreateURL(url,0,BASS_STREAM_BLOCK|BASS_STREAM_STATUS|BASS_STREAM_AUTOFREE|BASS_SAMPLE_FX|floatable,NULL,0);		
					Play_Chan(h);
					_RADIO_ = true;
					_PLAY_ = true;
					_PAUSE_ = false;
				}
				break;
				case 24:
				{
					_PAUSE_=false;
					_PLAY_=false;
					_RADIO_=false;
					MESS(17,TBM_SETRANGE,1,0);
					BASS_ChannelStop(can);
					BASS_StreamFree(can);
				}
				break;
				case 25:
				{   // toggle "rotate"
					if (MESS(25,BM_GETCHECK,0,0)) 
					{
						rotpos=0.7853981f;
						rotdsp=BASS_ChannelSetDSP(can,&Rotate,0,2);
					}else
						BASS_ChannelRemoveDSP(can,rotdsp);
				}
				break;
				case 26:
				{	// toggle "echo"
					if (MESS(26,BM_GETCHECK,0,0)) 
					{
						memset(echbuf,0,sizeof(echbuf));
						echpos=0;
						echdsp=BASS_ChannelSetDSP(can,&Echo,0,1);
					}else
						BASS_ChannelRemoveDSP(can,echdsp);
				}
				break;
				case 27:
				{   // toggle "flanger"
					if (MESS(27,BM_GETCHECK,0,0)) 
					{
						memset(flabuf,0,sizeof(flabuf));
						flapos=0;
					    flas=FLABUFLEN/2;
					    flasinc=0.002f;
						fladsp=BASS_ChannelSetDSP(can,&Flange,0,0);
					}else
						BASS_ChannelRemoveDSP(can,fladsp);
				}
				break;
				case 28:
				{
					if (MESS(27,BM_GETCHECK,0,0))
					{
						MESS(27,BM_SETCHECK,0,0);
						BASS_ChannelRemoveDSP(can,fladsp);
					}
					if (MESS(26,BM_GETCHECK,0,0))
					{
						MESS(26,BM_SETCHECK,0,0);
						BASS_ChannelRemoveDSP(can,echdsp);
					}
					if (MESS(25,BM_GETCHECK,0,0))
					{
						MESS(25,BM_SETCHECK,0,0);
						BASS_ChannelRemoveDSP(can,rotdsp);
					}
					SendMessage(GetDlgItem(h, 50),TBM_SETPOS,(WPARAM)1,10);
					SendMessage(GetDlgItem(h, 51),TBM_SETPOS,(WPARAM)1,10);
					SendMessage(GetDlgItem(h, 52),TBM_SETPOS,(WPARAM)1,10);
					SendMessage(GetDlgItem(h, 53),TBM_SETPOS,(WPARAM)1,20);
					UpdateFX(0,h);
					UpdateFX(1,h);
					UpdateFX(2,h);
					UpdateFX(3,h);
				}
				break;
				case 10:
				{
					if (HIWORD(w) == LBN_DBLCLK)
					{
						Buttom_Play(h);
					}

					if (HIWORD(l) == VK_RETURN )
					{
						
						Buttom_Play(h);
					}
				}
				break;
			}	
		case WM_HSCROLL:
		{
			if (l && LOWORD(w)!=SB_THUMBPOSITION && LOWORD(w)!=SB_ENDSCROLL) 
			{
				int p=SendMessage((HWND)l,TBM_GETPOS,0,0);
				switch (GetDlgCtrlID((HWND)l)) 
				{
					case 16:
					{
						BASS_SetConfig(BASS_CONFIG_GVOL_STREAM,p*100);
					}
					break;
					case 17:
					{
						if (l && LOWORD(w)!=SB_THUMBPOSITION && LOWORD(w)!=SB_ENDSCROLL) 
						{ 
							int pos=SendMessage((HWND)l,TBM_GETPOS,0,0);
							BASS_ChannelSetPosition(can,BASS_ChannelSeconds2Bytes(can,pos),BASS_POS_BYTE);
						}
					}
					break;
				}

			}
		}
		break;
		case WM_VSCROLL:
		{
			if (l) 
			{
				UpdateFX(GetDlgCtrlID((HWND)l)-50,h);
			}
		}
		break;
		case WM_INITDIALOG:
		{
			win=h;
			// initialize default output device
			if (!BASS_Init(-1,44100,0,win,NULL))
				Error("Can't initialize device");
			// initialize volume sliders
			MESS(16,TBM_SETRANGE,1,MAKELONG(0,100));
			MESS(16,TBM_SETPOS,1,100);
			MESS(26,TBM_SETRANGE,1,MAKELONG(0,100));
			MESS(26,TBM_SETPOS,1,100);
			MESS(34,TBM_SETRANGE,1,MAKELONG(0,100));
			MESS(34,TBM_SETPOS,1,100);
			MESS(43,TBM_SETRANGE,1,MAKELONG(0,100));
			MESS(43,TBM_SETPOS,1,BASS_GetVolume()*100);
			MESS(50,TBM_SETRANGE,FALSE,MAKELONG(0,20));
			MESS(50,TBM_SETPOS,TRUE,10);
			MESS(51,TBM_SETRANGE,FALSE,MAKELONG(0,20));
			MESS(51,TBM_SETPOS,TRUE,10);
			MESS(52,TBM_SETRANGE,FALSE,MAKELONG(0,20));
			MESS(52,TBM_SETPOS,TRUE,10);
			MESS(53,TBM_SETRANGE,FALSE,MAKELONG(0,20));
			MESS(53,TBM_SETPOS,TRUE,20);
			SetTimer(h,1,25,NULL);
			SetTimer(h,100,150,NULL);

			memset(&ofn,0,sizeof(ofn));
			ofn.lStructSize=sizeof(ofn);
			ofn.hwndOwner=h;
			ofn.nMaxFile=MAX_PATH;
			ofn.Flags=OFN_HIDEREADONLY|OFN_EXPLORER;
			floatable=BASS_StreamCreate(44100,2,BASS_SAMPLE_FLOAT,NULL,0);
			if (floatable) 
			{
				BASS_StreamFree(floatable);
				floatable=BASS_SAMPLE_FLOAT;
			}
		}
		return 1;
		case WM_DESTROY:
		{
			KillTimer(h,1);
			BASS_Free(); // close output
		}
		break;
	}
	return 0;
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,LPSTR lpCmdLine, int nCmdShow)
{
	// check the correct BASS was loaded
	if (HIWORD(BASS_GetVersion())!=BASSVERSION) 
	{
		MessageBox(0,LPSTR("An incorrect version of BASS.DLL was loaded"),0,MB_ICONERROR);
		return 0;
	}
	// display the window
	DialogBox(hInstance,MAKEINTRESOURCE(1000),NULL,&dialogproc);

	return 0;
}