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

// Byte-oriented boot selector datapath.

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        if (y == 8'h00)
            y <= 8'h10;
        else
            y <= 8'h20;
    end else if (en)
        y <= a;
end

endmodule
`default_nettype wire
