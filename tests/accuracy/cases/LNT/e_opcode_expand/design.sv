`default_nettype none
// Opcode expand.
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
    function [7:0] expand(input gate, input [7:0] payload);
        begin
            if (gate) expand = payload ^ 8'h80;
        end
    endfunction
    always @* value = expand(enable, data);
endmodule

`default_nettype wire
