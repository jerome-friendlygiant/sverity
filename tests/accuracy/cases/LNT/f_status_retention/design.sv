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




// Byte-oriented status retention datapath.

always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
        y <= 8'h24;
end

endmodule
`default_nettype wire
