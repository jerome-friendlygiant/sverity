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

// Byte-oriented merged write queue datapath.

logic [7:0] queue [0:3];
always @(posedge clk) begin
    if (en)
        queue[addr] <= a;
    else if (b[0])
        queue[addr_b] <= b;
end
assign y = queue[2'd0];

endmodule
`default_nettype wire
