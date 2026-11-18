// Priority lane scan.
// Interface values are sampled by the downstream transaction stage.
// Combinational paths preserve the transaction qualifier.

module top (
    input wire [3:0] a,

    input wire [7:0] b,

    input wire valid,

    output wire [4:0] y,

    output wire ready

);

    // Datapath and transaction response.

    genvar lane;
    generate
        for (lane=0; lane<5; lane=lane+1) begin : lanes
            assign y[lane] = a[lane];
        end
    endgenerate
    wire [7:0] tag;
    assign tag = b;

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
