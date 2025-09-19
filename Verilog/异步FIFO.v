module async_fifo #(
parameter DATA_WIDTH = 8,
parameter DATA_DEPTH = 8,
parameter PTR_WIDTH = 3   
)(
    input wr_clk,
    input wr_rst_n,
    input wr_en,
    input [DATA_WIDTH-1:0] din,

    input rd_clk,
    input rd_rst_n,
    input rd_en,
    output reg [DATA_WIDTH-1:0] dout,

    output reg full,
    output reg empty  
)

reg [DATA_WIDTH-1:0] mem[DATA_DEPTH-1:0];

assign wr_inc = !full & wr_en;
reg [PTR_WIDTH:0] wr_ptr, wr_ptr_next;
assign wr_ptr_next = wr_ptr + wr_inc;
always@(posedge wr_clk or negedge wr_rst_n)begin
    if(!wr_rst_n)begin
        wr_ptr <= 1'b0;
    end
    else begin
        wr_ptr <= wr_ptr_next;
        if(wr_inc)
            mem[wr_ptr[PTR_WIDTH-1:0]] <= din;
    end
end

assign rd_inc = !empty & rd_en;
reg [PTR_WIDTH:0] rd_ptr, rd_ptr_next;
assign rd_ptr_next = rd_ptr + rd_inc;
always@(posedge rd_clk or negedge rd_rst_n)begin
    if(!rd_rst_n)begin
        rd_ptr <= 1'b0;
        dout <= 0;
    end
    else begin
        rd_ptr <= rd_ptr_next;
        if(rd_inc)
            dout <= mem[rd_ptr[PTR_WIDTH-1:0]];
    end
end

wire [PTR_WIDTH:0] gray_rd_ptr, gray_wr_ptr;
assign gray_wr_ptr = wr_ptr ^ (wr_ptr>>1);
assign gray_rd_ptr = rd_ptr ^ (rd_ptr>>1);
reg [PTR_WIDTH:0] gray_wr_ptr_r, gray_wr_ptr_rr, gray_rd_ptr_r, gray_rd_ptr_rr;
always@(posedge wr_clk or negedge wr_rst_n)begin
    if(!wr_rst_n)begin
        gray_rd_ptr_r <= 0;
        gray_rd_ptr_rr<= 0;
    end
    else begin
        gray_rd_ptr_r <= gray_rd_ptr;
        gray_rd_ptr_rr<= gray_rd_ptr_r;
    end
end
always@(posedge rd_clk or negedge rd_rst_n)begin
    if(!rd_rst_n)begin
        gray_wr_ptr_r <= 0;
        gray_wr_ptr_rr<= 0;
    end
    else begin
        gray_wr_ptr_r <= gray_wr_ptr;
        gray_wr_ptr_rr<= gray_wr_ptr_r;
    end
end
always@(*)begin
    if(gray_wr_ptr == {~gray_rd_ptr_rr[PTR_WIDTH:PTR_WIDTH-1], gray_rd_ptr_rr})
        full <= 1'b1;
    else 
        full <= 1'b0;
end
always@(*)begin
    if(gray_rd_ptr == gray_wr_ptr_rr)
        empty <= 1'b1;
    else 
        empty <= 1'b0;
end
endmodule
// always@(*)begin
//     if(gray_rd_ptr_rr == {~gray_wr_ptr_rr[PTR_WIDTH:PTR_WIDTH-1], gray_wr_ptr_rr})
//         full <= 1'b1;
//     else 
//         full <= 1'b0;
// end
// always@(*)begin
//     if(gray_rd_ptr_rr == gray_wr_ptr_rr)
//         empty <= 1'b1;
//     else 
//         empty <= 1'b0;
// end
// endmodule