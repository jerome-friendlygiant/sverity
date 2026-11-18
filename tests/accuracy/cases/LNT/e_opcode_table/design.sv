`default_nettype none
// Opcode table.
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
    function automatic [7:0] decode(input gate, input [7:0] payload);
        begin
            if (gate) decode = payload;
            else decode = 8'h00;
        end
    endfunction
    always @* value = decode(enable, data);
endmodule

`default_nettype wire
