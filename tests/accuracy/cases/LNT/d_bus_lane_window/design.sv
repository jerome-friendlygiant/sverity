// Bus lane window.
// Interface values are sampled by the downstream transaction stage.
// Combinational paths preserve the transaction qualifier.

module top (
    input wire [7:0] a,

    input wire [3:0] b,

    input wire valid,

    output wire [3:0] y,

    output wire ready

);

    // Datapath and transaction response.

    assign y = (b <= 4'd4) ? a[b +: 4] : 4'b0000;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
