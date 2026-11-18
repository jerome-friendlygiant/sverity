`default_nettype none
// Telemetry lane.
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
    telemetry u_telemetry(.permit(enable), .result(value));
endmodule

module telemetry(input wire permit, output wire [7:0] result);
    lane u_inner(.permit(permit), .result(result));
endmodule

module lane #(parameter integer WIDTH=8)(input wire [WIDTH-1:0] payload, input wire permit, output wire [WIDTH-1:0] result);
    assign result = permit ? payload : {WIDTH{1'b0}};
endmodule

`default_nettype wire
