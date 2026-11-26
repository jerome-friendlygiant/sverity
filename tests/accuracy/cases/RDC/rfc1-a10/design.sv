// Event counter whose reset is released through a two-stage synchroniser.
module top (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       ev,
    output logic [7:0] cnt
);
    logic [1:0] rst_sync;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) rst_sync <= 2'b00;
        else        rst_sync <= {rst_sync[0], 1'b1};
    end

    always_ff @(posedge clk or negedge rst_sync[1]) begin
        if (!rst_sync[1]) cnt <= '0;
        else if (ev)      cnt <= cnt + 1'b1;
    end
endmodule
