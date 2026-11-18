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

// Byte-oriented port selector datapath.

always @* begin
    y = 8'd0;
    case (addr) // synthesis full_case
        2'd0: y = a;
        2'd1: y = b;
        2'd2: y = a ^ b;
        2'd3: y = a & b;
    endcase
end

endmodule
`default_nettype wire
