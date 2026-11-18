`default_nettype none
module top (
    input wire clk,
    input wire aux_clk,
    input wire rst_n,
    input wire en,
    input wire [7:0] a,
    input wire [7:0] b,
    input wire [1:0] addr,
    input wire [1:0] addr_b,
    output logic [7:0] y
);



// Byte-oriented programmable ratio datapath.

logic [7:0] numerator;
logic [7:0] denominator;
assign numerator = a;
assign denominator = b;
assign y = numerator / denominator;

endmodule
`default_nettype wire
