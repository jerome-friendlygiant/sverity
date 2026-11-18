`default_nettype none
// Startup priority.
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
    reg [1:0] command;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) command <= 2'd3;
        else if (enable) command <= sel;
    always @* begin
        value = 8'h00;
        priority case (command)
            2'd0: value = data;
            2'd1: value = ~data;
            2'd2: value = data ^ 8'h55;
        endcase
    end
endmodule

`default_nettype wire
