%% author:hffan
%% date:2024-08-21 17:21:58
%% t = tcpclient('192.168.1.101', 8060)

clear all;
close all;
clc;
delete *.asv;


%% SELECT FILE
[filename ,pathname, filter] = uigetfile('*.bin','select a file');
if filter == 2
    return
end


%% OPEN FILE
file_string = fullfile(pathname,filename);
fid = fopen(file_string,'rb');


%% READ BINARY FILE
readlen=inf;
receive_data =fread(fid,readlen,'uint8');


%% CLOSE FILE
fclose(fid);


%% SPLIT CHANNELS
package_number=length(receive_data);
package_length=1220;

header = receive_data(1:20);
header_start = dec2hex(header(2)*256 + header(1));
header_framecount = dec2hex(header(3)*256 + header(4));
header_cardtype = dec2hex(header(4)*256 + header(5));
header_serialnumber = dec2hex(header(6)*256 + header(7));
header_samplerate = header(8)*256 + header(9);
header_range = header(10)*256 + header(11);
header_channel = header(12)*256 + header(13);


data = receive_data(21:1220);
number = length(data);


%% one sample have 2bytes
for i=1:number/2
    signal_code(i) = double((data(2*i-1)*256 + data(2*i)));
end


%% PLOT DATA
figure(1);
hold on;
plot(signal_code,'r-*');
ylabel('Code');
n=length(signal_code);




figure(2);
hold on;
CHA_code=signal_code(1:4:end);
CHB_code=signal_code(2:4:end);
CHC_code=signal_code(3:4:end);
CHD_code=signal_code(4:4:end);
plot(CHA_code,'r-*');
plot(CHB_code,'b-*');
plot(CHC_code,'m-*');
plot(CHD_code,'g-*');
ylabel('Code');
N=length(CHA_code);



figure(3);
CHA_code=signal_code(1:4:end);
CHB_code=signal_code(2:4:end);
CHC_code=signal_code(3:4:end);
CHD_code=signal_code(4:4:end);
subplot(411);plot(CHA_code,'r-*');ylabel('Code');
subplot(412);plot(CHB_code,'b-*');ylabel('Code');
subplot(413);plot(CHC_code,'m-*');ylabel('Code');
subplot(414);plot(CHD_code,'g-*');ylabel('Code');
N=length(CHA_code);



figure(4);
range=2000;     %¡À1V is 2000mV
resolution=16;
hold on;
CHA_voltage=range*CHA_code/2^(resolution);
plot(CHA_voltage,'r-*');
ylabel('mV');



figure(5);
fs=256e3;%1e6
dB=20*log10(abs(fft(CHA_voltage)));
plot(fs.*(0:N-1)/N,dB);


