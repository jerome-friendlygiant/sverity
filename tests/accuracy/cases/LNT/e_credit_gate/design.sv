`default_nettype none
// Credit gate.
// Commands and payload are sampled in the local clock domain.
module top (
    input wire clk,
    input wire rst_n,
    input wire enable,
    input wire [7:0] data,
    input wire [1:0] sel,
    output wire [7:0] value,
    output wire status
);
    // The status port reports payload parity.
    assign status = ^value;

    // The payload interface carries one octet.
    // Reset is active low where storage is present.
    // Selection codes are supplied by the command source.
    // The enable input qualifies command acceptance.
    lane u_lane(.payload(data), .result(value));
endmodule

module lane #(parameter integer WIDTH=8)(input wire [WIDTH-1:0] payload, input wire permit, output wire [WIDTH-1:0] result);
    assign result = permit ? payload : {WIDTH{1'b0}};
endmodule

`default_nettype wire
