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

// Byte-oriented normalized rate datapath.

logic [7:0] denominator;
always_comb begin
    if (b == 8'd0)
        denominator = 8'd1;
    else
        denominator = b;
end
assign y = a / denominator;

endmodule
`default_nettype wire
