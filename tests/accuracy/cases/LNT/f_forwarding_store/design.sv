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

// Byte-oriented forwarding store datapath.

logic [7:0] store [0:3];
always @(posedge clk) begin
    if (en)
        store[addr] <= a;
    if (en && addr == addr_b)
        y <= a;
    else
        y <= store[addr_b];
end

endmodule
`default_nettype wire
