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



// Byte-oriented quota alarm datapath.

logic alarm;
always @* begin
    alarm = (a < 8'd0);
end
assign y = {7'd0, alarm};

endmodule
`default_nettype wire
