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

// Byte-oriented command decode datapath.

always @* begin
    case (a) // synthesis parallel_case
        8'h12: y = 8'h01;
        8'h34: y = 8'h02;
        8'h56: y = 8'h04;
        default: y = 8'h00;
    endcase
end

endmodule
`default_nettype wire
