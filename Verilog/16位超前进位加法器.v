module adder4(
    input [3:0] A,
    input [3:0] B,
    input C0,
    output[3:0] S,
    output Cout, 
    output Gm,
    output Pm
);
wire [3:0] g,p;
wire [4:0]C;
assign g[0] = A[0] & B[0];
assign g[1] = A[1] & B[1];
assign g[2] = A[2] & B[2];
assign g[3] = A[3] & B[3];
assign p[0] = A[0] | B[0];
assign p[1] = A[1] | B[1];
assign p[2] = A[2] | B[2];
assign p[3] = A[3] | B[3];
assign Cout = C[4];
assign Gm = g[3] | p[3] & g[2] | p[3] & p[2] & g[1] | p[3] & p[2] & p[1] & g[0];
assign Pm = p[3] & p[2] & p[1] & p[0];
adder adder0(
    .A(A[0]),
    .B(B[0]),
    .C0(C0),
    .S(S[0])
);    
adder adder1(
    .A(A[1]),
    .B(B[1]),
    .C0(C[1]),
    .S(S[1])
);    
adder adder2(
    .A(A[2]),
    .B(B[2]),
    .C0(C[2]),
    .S(S[2])
);    
adder adder3(
    .A(A[3]),
    .B(B[3]),
    .C0(C[3]),
    .S(S[3])
);    
CLA_4 CLA_4(
    .g(g),
    .p(p),
    .Cin(C0),
    .Cout(C)
);

endmodule

module adder(
    input A,
    input B,
    input C0,
    output S,
    output Cout
);
assign S=A^B^C0;
assign Cout=A&B|((A|B)&C0);
endmodule

module CLA_4(
    input [3:0] g,
    input [3:0] p,
    input Cin,
    output [4:0]Cout
);
assign Cout[0] = Cin;
assign Cout[1] = g[0] | p[0] & Cin;
assign Cout[2] = g[1] | p[1] & Cout[1];
assign Cout[3] = g[2] | p[2] & Cout[2];
assign Cout[4] = g[3] | p[3] & Cout[3];
endmodule

module adder16(
    input [15:0] A,
    input [15:0] B,
    input C0,
    output[15:0] S,
    output Cout 
);
wire [3:0] G,P;
wire [4:0] C;
assign Cout = C[4];
adder4 adder_1(
    .A(A[3:0]),
    .B(B[3:0]),
    .C0(C0),
    .S(S[3:0]),
    .Cout(),
    .Gm(G[0]),
    .Pm(P[0])
);
adder4 adder_2(
    .A(A[7:4]),
    .B(B[7:4]),
    .C0(C[1]),
    .S(S[7:4]),
    .Cout(),
    .Gm(G[1]),
    .Pm(P[1])
);
adder4 adder_3(
    .A(A[11:8]),
    .B(B[11:8]),
    .C0(C[2]),
    .S(S[11:8]),
    .Cout(),
    .Gm(G[2]),
    .Pm(P[2])
);
adder4 adder_4(
    .A(A[15:12]),
    .B(B[15:12]),
    .C0(C[3]),
    .S(S[15:12]),
    .Cout(),
    .Gm(G[3]),
    .Pm(P[3])
);
CLA_4 CLA_4(
    .g(G),
    .p(P),
    .Cin(C0),
    .Cout(C)
);
endmodule