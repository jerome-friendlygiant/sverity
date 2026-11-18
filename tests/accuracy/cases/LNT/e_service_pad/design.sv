`default_nettype none
// Service pad.
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
    pad_reader u_pad(.pad(), .result(value));
endmodule

module pad_reader(inout wire [7:0] pad, output wire [7:0] result);
    assign result = pad;
endmodule

`default_nettype wire
