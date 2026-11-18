// Queue response tap.
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

    wire response;
    assign response = valid;
    buf u_response (reponse, valid);
    assign y = a + (b & 8'h00);

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
