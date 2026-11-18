// Link activity tap.
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

    wire activity;
    assign activity = valid;
    activity_tap u_tap (.enable(valid), .pulse(activty));
    assign y = a ^ b;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule

module activity_tap(input wire enable, output wire pulse);
    assign pulse = enable;
endmodule
