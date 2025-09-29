module delay_clap(
    input clk1,
    input clk2,
    input rst_n,
    input din,
    output dout
);
reg [3:0] din_r;
always@(posedge clk1 or negedge rst_n)begin
    if(!rst_n)
        din_r[0] <= 1'b0;
    else
        din_r[0] <= din?~din_r[0]:din_r[0];
end
always@(posedge clk or negedge rst_n)begin
    if(!rst_n)
        din_r[3:1] <= 3'b0;
    else
        din_r[3:1] <= din_r[2:0];
end
assign dout = din_r[2] ^ din_r[3];
endmodule