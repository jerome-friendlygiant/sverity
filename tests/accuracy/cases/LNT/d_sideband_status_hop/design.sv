// Sideband status hop.
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

    status_hop u_hop (.request(a), .mask(b), .status(y));

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule

module status_hop(
    input wire [7:0] request,
    input wire [7:0] mask,
    output wire [7:0] status
);
    wire [7:0] summary;
    assign summary = request & mask;
endmodule
