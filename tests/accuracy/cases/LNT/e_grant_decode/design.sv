`default_nettype none
// Grant decode.
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
    wire request, grant;
    wire [1:0] permissions;
    genvar bit_index;
    generate for (bit_index=0; bit_index<2; bit_index=bit_index+1) begin : g_permission
        assign permissions[bit_index] = sel[bit_index];
    end endgenerate
    assign request = enable | permissions[0];
    assign grant = request & permissions[1];
    always @* value = {7'b0, grant};
endmodule

`default_nettype wire
