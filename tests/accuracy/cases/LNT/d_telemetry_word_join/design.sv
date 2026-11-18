// Telemetry word join.
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

    localparam integer FIELD = 4;
    wire [FIELD-1:0] fields [0:1];
    assign fields[0] = a[3:0];
    assign fields[1] = b[3:0];
    assign y = {fields[1], fields[0]};

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
