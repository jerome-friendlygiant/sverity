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

// Byte-oriented epoch counter datapath.

always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
        y <= 8'd0;
    else if (en) begin
        y <= y + 8'd1;
        // synthesis translate_off
        y <= y + 8'd2;
        // synthesis translate_on
    end
end

endmodule
`default_nettype wire
