module delay_clap(
    input clk1,
    input clk2,
    input rst_n,
    input din,
    output dout
);
reg [2:0] din_r;
always@(posedge clk2 or negedge rst_n)begin
    if(!rst_n)
        din_r <= 3'b0;
    else
        din_r <= {din_r[1:0],din};
end
assign dout = din_r[1] & !din_r[2];
endmodule