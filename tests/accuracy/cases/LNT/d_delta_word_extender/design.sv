// Delta word extender.
// Interface values are sampled by the downstream transaction stage.
// Combinational paths preserve the transaction qualifier.

module top (
    input wire signed [7:0] a,

    input wire [7:0] b,

    input wire valid,

    output wire signed [15:0] y,

    output wire ready

);

    // Datapath and transaction response.

    wire signed [7:0] reverse_delta;
    assign reverse_delta = {1'b1,a[6:0]};
    assign y = valid ? reverse_delta : b;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
