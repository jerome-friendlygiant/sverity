`default_nettype none
// Token mesh.
// Commands and payload are sampled in the local clock domain.
module top (
    input wire clk,
    input wire rst_n,
    input wire enable,
    input wire [7:0] data,
    input wire [1:0] sel,
    output reg [7:0] value,
    output wire status
);
    // The status port reports payload parity.
    assign status = ^value;

    // The payload interface carries one octet.
    // Reset is active low where storage is present.
    // Selection codes are supplied by the command source.
    // The enable input qualifies command acceptance.
    wire a, b, c;
    assign a = b & enable;
    assign b = c | sel[0];
    assign c = a & sel[1];
    always @* value = {7'b0, a};
endmodule

`default_nettype wire
