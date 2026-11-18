// Split status formatter.
// Interface values are sampled by the downstream transaction stage.
// Combinational paths preserve the transaction qualifier.

module top (
    input wire [7:0] a,

    input wire [7:0] b,

    input wire valid,

    output wire [7:0] y,

    output wire ready

);

    // Datapath and transaction response.

    typedef struct packed {
        logic [3:0] state_code;
        logic [3:0] lane_code;
    } status_t;
    wire status_t status_word;
    assign status_word.state_code = a[7:4];
    assign status_word.lane_code = b[3:0];
    assign y = status_word;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
