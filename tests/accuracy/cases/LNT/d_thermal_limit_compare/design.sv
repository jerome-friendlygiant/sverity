// Thermal limit compare.
// Interface values are sampled by the downstream transaction stage.
// Combinational paths preserve the transaction qualifier.

module top (
    input wire signed [7:0] a,

    input wire [7:0] b,

    input wire valid,

    output wire  y,

    output wire ready

);

    // Datapath and transaction response.

    wire signed [8:0] temp;
    wire signed [8:0] limit_value;
    assign temp = {a[7],a};
    assign limit_value = {1'b0,b};
    assign y = temp < limit_value;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
