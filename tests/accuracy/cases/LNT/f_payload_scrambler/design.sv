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


// Byte-oriented payload scrambler datapath.

always_comb begin
    y = a;
    // synthesis translate_off
    y = a ^ 8'h01;
    // synthesis translate_on
end

endmodule
`default_nettype wire
