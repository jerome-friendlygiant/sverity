// Merged lane outputs.
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

    lane_copy u_a (.value(a), .result(y));
    lane_copy u_b (.value(b), .result(y));

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule

module lane_copy(input wire [7:0] value, output wire [7:0] result);
    assign result = value;
endmodule
