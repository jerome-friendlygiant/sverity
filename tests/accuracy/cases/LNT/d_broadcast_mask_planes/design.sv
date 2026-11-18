// Broadcast mask planes.
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

    genvar plane;
    generate
        for (plane=0; plane<2; plane=plane+1) begin : planes
            assign y = (plane == 0) ? a : b;
        end
    endgenerate

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
