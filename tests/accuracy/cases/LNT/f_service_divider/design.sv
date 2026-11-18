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

// Byte-oriented service divider datapath.

logic divided_clk;
always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
        divided_clk <= 1'b0;
    else
        divided_clk <= ~divided_clk;
end
always @(posedge divided_clk or negedge rst_n) begin
    if (!rst_n)
        y <= 8'd0;
    else if (en)
        y <= a;
end

endmodule
`default_nettype wire
