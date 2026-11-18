`default_nettype none
// Configuration word.
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
    function automatic [7:0] pattern(input [7:0] seed);
        integer i;
        begin
            for (i=0; i<8; i=i+1) pattern[i] = seed[7-i];
        end
    endfunction
    parameter [7:0] SEED = 8'h96;
    localparam [7:0] PATTERN = pattern(SEED);
    always @* value = enable ? PATTERN : data;
endmodule

`default_nettype wire
