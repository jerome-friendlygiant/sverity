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

// Byte-oriented dual request table datapath.

logic [7:0] entries [0:3];
always @(posedge clk) begin
    if (en)
        entries[addr] <= a;
end
always @(posedge clk) begin
    if (b[0])
        entries[addr_b] <= b;
end
assign y = entries[2'd0];

endmodule
`default_nettype wire
