`default_nettype none
// Mode register.
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
    typedef enum logic [1:0] {IDLE, LOAD, SEND, WAITING} mode_t;
    mode_t mode;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) mode <= IDLE;
        else case (mode)
            IDLE: if (enable) mode <= LOAD;
            LOAD: mode <= SEND;
            SEND: mode <= WAITING;
            WAITING: mode <= IDLE;
        endcase
    always @* begin
        case (mode)
            IDLE: value = 8'h00;
            LOAD: value = data;
            SEND: value = ~data;
            WAITING: value = 8'hff;
        endcase
    end
endmodule

`default_nettype wire
