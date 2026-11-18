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



// Byte-oriented rate conversion datapath.

parameter logic [7:0] RATE_SCALE = 8'd0;
function automatic [7:0] convert(input logic [7:0] value);
    convert = value / RATE_SCALE;
endfunction
assign y = convert(a);

endmodule
`default_nettype wire
