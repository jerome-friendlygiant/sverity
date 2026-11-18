// Position delta select.
// Interface values are sampled by the downstream transaction stage.
// Combinational paths preserve the transaction qualifier.

module top (
    input wire signed [7:0] a,

    input wire signed [7:0] b,

    input wire valid,

    output wire signed [15:0] y,

    output wire ready

);

    // Datapath and transaction response.

    assign y = valid ? a : b;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
