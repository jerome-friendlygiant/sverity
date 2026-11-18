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

// Byte-oriented routing map datapath.

logic [7:0] routes [0:3];
integer k;
always @* begin
    for (k = 0; k < 4; k = k + 1)
        routes[k] = a ^ b;
end
assign y = routes[addr];

endmodule
`default_nettype wire
