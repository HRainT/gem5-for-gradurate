//存在两个同步的倍频时钟clk0 clk1,已知clk0是clk1的二倍频，现在要设计一个切换电路，sel选择时候进行切换，要求没有毛刺。
//https://blog.csdn.net/qq_21842097/article/details/118324617
`timescale 1ns/1ns
 
module huawei6(
    input wire clk0  ,
    input wire clk1  ,
    input wire rst  ,
    input wire sel ,
    output wire clk_out
);
//*************code***********//
    reg sel1, sel0;
     
    always @ (negedge clk0, negedge rst) begin
        if(!rst)
            sel1 <=0;
        else
            sel1 <= sel & ~sel0;
    end
     
    always @ (negedge clk1, negedge rst) begin
        if(!rst)
            sel0 <=0;
        else
            sel0 <= ~sel & ~sel1;
    end 
     
    assign  clk_out = (sel1 & clk1) | (sel0 & clk0);

//clk和sel不在同一个域是为了避免控制信号(sel)变化时数据信号(clk)在边沿不稳定,导致输出有毛刺
 
//*************code***********//
endmodule