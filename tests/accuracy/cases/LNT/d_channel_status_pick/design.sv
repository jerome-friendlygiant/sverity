// Channel status pick.
// Interface values are sampled by the downstream transaction stage.
// Combinational paths preserve the transaction qualifier.

module top (
    input wire [7:0] a,

    input wire [2:0] b,

    input wire valid,

    output wire  y,

    output wire ready

);

    // Datapath and transaction response.

    function automatic pick_lane(input [7:0] lanes, input [2:0] index);
        pick_lane = lanes[index];
    endfunction
    assign y = pick_lane(a,b);

    // Response qualifier follows the request qualifier.
    assign ready = valid;

endmodule
