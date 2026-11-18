// Servo error scaler.
// Interface values are sampled by the downstream transaction stage.
// Combinational paths preserve the transaction qualifier.

module top (
    input wire signed [7:0] a,

    input wire [7:0] b,

    input wire valid,

    output wire [7:0] y,

    output wire ready

);

    // Datapath and transaction response.

    wire signed [7:0] reverse_error;
    assign reverse_error = {1'b1,a[6:0]};
    assign y = (reverse_error & b) >>> 1;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
