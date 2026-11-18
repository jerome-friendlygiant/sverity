`default_nettype none
// Dispatch decode.
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
    typedef struct packed {logic valid; logic [7:0] payload;} command_t;
    command_t decoded;
    always @* begin
        decoded.valid = enable;
        decoded.payload = data;
        value = decoded.valid ? decoded.payload : 8'h00;
    end
endmodule

`default_nettype wire
