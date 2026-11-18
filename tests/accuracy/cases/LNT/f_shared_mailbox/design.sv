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

// Byte-oriented shared mailbox datapath.

logic [7:0] mailbox [0:3];
always @(posedge clk) begin
    if (en)
        mailbox[addr] <= a;
end
always @(posedge aux_clk) begin
    if (en)
        mailbox[addr_b] <= b;
end
assign y = mailbox[2'd0];

endmodule
`default_nettype wire
