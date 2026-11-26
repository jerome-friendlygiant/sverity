// Pipeline register block whose reset is released through three synchroniser stages.
module top (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] d,
    output logic [7:0] q
);
    logic [2:0] rst_sync;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) rst_sync <= 3'b000;
        else        rst_sync <= {rst_sync[1:0], 1'b1};
    end

    always_ff @(posedge clk or negedge rst_sync[2]) begin
        if (!rst_sync[2]) q <= '0;
        else              q <= d;
    end
endmodule
