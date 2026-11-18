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

// Byte-oriented transfer gate datapath.

wire transfer_clk = clk & en;
always @(posedge transfer_clk or negedge rst_n) begin
    if (!rst_n)
        y <= 8'd0;
    else
        y <= a;
end

endmodule
`default_nettype wire
