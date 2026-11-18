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



// Byte-oriented window accept datapath.

logic [2:0] phase;
logic accepted;
assign phase = a[2:0];
assign accepted = ({1'b0, phase} < 4'd8);
assign y = {7'd0, accepted};

endmodule
`default_nettype wire
