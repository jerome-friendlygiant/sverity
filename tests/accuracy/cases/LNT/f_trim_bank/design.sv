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

// Byte-oriented trim bank datapath.

logic [7:0] trim [0:3];
generate
    for (genvar lane = 0; lane < 4; lane = lane + 1) begin : lanes
        always_comb trim[lane] = a & b;
    end
endgenerate
assign y = trim[addr];

endmodule
`default_nettype wire
