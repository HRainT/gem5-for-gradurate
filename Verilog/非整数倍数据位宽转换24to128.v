/*
实现数据位宽转换电路，实现24bit数据输入转换为128bit数据输出。其中，先到的数据应置于输出的高bit位。
输入数据是24bit,输出数据是128bit，输出数据位宽不是输入数据位宽的整数倍，该如何解决呢？

那么就需要计算输入数据位宽和输出数据位宽的最小公倍数，根据公倍数得到倍数关系，进行位宽拼接转换。

在这里，24与128的最小公倍数为384，也就是说每输入16个数据，为一轮，就可以产生3个完整的128比特输出
原文链接：https://blog.csdn.net/weixin_46897065/article/details/139701267
*/
`timescale 1ns/1ns
 
module width_24to128(
	input 				clk 		,   
	input 				rst_n		,
	input				valid_in	,
	input	[23:0]		data_in		,
 
 	output	reg			valid_out	,
	output  reg [127:0]	data_out
);
	reg [3  :0]  data_cnt;
	reg [119:0] data_temp;
 
	always@(posedge clk or negedge rst_n)
	if(!rst_n)
		data_cnt <= 0;
	else if (valid_in)
		data_cnt <= data_cnt +1;
	else
		data_cnt <= data_cnt;
	
	always@(posedge clk or negedge rst_n)
	if(!rst_n)
		data_temp <= 0;
	else if(valid_in)
		data_temp <= {data_temp[95:0],data_in};
	
	always@(posedge clk or negedge rst_n)
	if(!rst_n)
		data_out <= 0;
	else if(data_cnt == 5)
		data_out <= {data_temp,data_in[23:16]}; // data_in[23:16] 共8bit == data_temp[119:0]- data_temp[111:0]共8bit
	else if(data_cnt == 10)
		data_out <= {data_temp[111:0], data_in[23:8]}; // data_in[23:8] 共16bit == data_temp[119:0]- data_temp[103:0]共16bit
	else if(data_cnt == 15)
		data_out <= {data_temp[103:0], data_in};
	else
		data_out <= data_out;
 
	always@(posedge clk or negedge rst_n)
	if(!rst_n)
		valid_out <= 0;
	else if (data_cnt == 5)
		valid_out <= 1;
	else if (data_cnt == 10)
		valid_out <= 1;
	else if (data_cnt == 15)
		valid_out <= 1;
	else
		valid_out <= 0;
 
endmodule