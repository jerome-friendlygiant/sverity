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

// Byte-oriented sample exchange datapath.

logic [7:0] samples [0:3];
always @(posedge clk) begin
    samples[2'd2] <= a;
end
always @(negedge aux_clk) begin
    y <= samples[2'd2];
end

endmodule
`default_nettype wire
