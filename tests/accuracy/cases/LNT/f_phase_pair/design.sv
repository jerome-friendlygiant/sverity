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

// Byte-oriented phase pair datapath.

logic [3:0] rising_word;
logic [3:0] falling_word;
always @(posedge clk or negedge rst_n) begin
    if (!rst_n) rising_word <= 4'd0;
    else if (en) rising_word <= a[7:4];
end
always @(negedge clk or negedge rst_n) begin
    if (!rst_n) falling_word <= 4'd0;
    else if (en) falling_word <= a[3:0];
end
assign y = {rising_word, falling_word};

endmodule
`default_nettype wire
