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

// Byte-oriented event trace datapath.

logic [7:0] trace_mem [0:3];
always @(posedge clk) begin
    trace_mem[2'd0] <= a;
end
always @(posedge aux_clk) begin
    y <= trace_mem[2'd0];
end

endmodule
`default_nettype wire
