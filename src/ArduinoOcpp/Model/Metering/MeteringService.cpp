// matth-x/ArduinoOcpp
// Copyright Matthias Akstaller 2019 - 2023
// MIT License

#include <ArduinoOcpp/Model/Metering/MeteringService.h>
#include <ArduinoOcpp/Model/Transactions/Transaction.h>
#include <ArduinoOcpp/Context.h>
#include <ArduinoOcpp/Core/FilesystemAdapter.h>
#include <ArduinoOcpp/Core/SimpleRequestFactory.h>
#include <ArduinoOcpp/Operations/MeterValues.h>
#include <ArduinoOcpp/Debug.h>

using namespace ArduinoOcpp;

MeteringService::MeteringService(Context& context, int numConn, std::shared_ptr<FilesystemAdapter> filesystem)
      : context(context), meterStore(filesystem) {

    for (int i = 0; i < numConn; i++) {
        connectors.push_back(std::unique_ptr<MeteringConnector>(new MeteringConnector(context.getModel(), i, meterStore)));
    }

    /*
     * Register further message handlers to support echo mode: when this library
     * is connected with a WebSocket echo server, let it reply to its own requests.
     * Mocking an OCPP Server on the same device makes running (unit) tests easier.
     */
    context.getOperationRegistry().registerOperation("MeterValues", [] () {
        return new Ocpp16::MeterValues();});
}

void MeteringService::loop(){

    for (unsigned int i = 0; i < connectors.size(); i++){
        auto meterValuesMsg = connectors[i]->loop();
        if (meterValuesMsg != nullptr) {
            auto meterValues = makeRequest(std::move(meterValuesMsg));
            meterValues->setTimeout(120000);
            context.initiateRequest(std::move(meterValues));
        }
    }
}

void MeteringService::addMeterValueSampler(int connectorId, std::unique_ptr<SampledValueSampler> meterValueSampler) {
    if (connectorId < 0 || connectorId >= (int) connectors.size()) {
        AO_DBG_ERR("connectorId is out of bounds");
        return;
    }
    connectors[connectorId]->addMeterValueSampler(std::move(meterValueSampler));
}

std::unique_ptr<SampledValue> MeteringService::readTxEnergyMeter(int connectorId, ReadingContext context) {
    if (connectorId < 0 || (size_t) connectorId >= connectors.size()) {
        AO_DBG_ERR("connectorId is out of bounds");
        return nullptr;
    }
    return connectors[connectorId]->readTxEnergyMeter(context);
}

std::unique_ptr<Request> MeteringService::takeTriggeredMeterValues(int connectorId) {
    if (connectorId < 0 || connectorId >= (int) connectors.size()) {
        AO_DBG_ERR("connectorId out of bounds. Ignore");
        return nullptr;
    }
    auto& connector = connectors.at(connectorId);
    if (connector.get()) {
        auto msg = connector->takeTriggeredMeterValues();
        if (msg) {
            auto meterValues = makeRequest(std::move(msg));
            meterValues->setTimeout(120000);
            return meterValues;
        }
        AO_DBG_DEBUG("Did not take any samples for connectorId %d", connectorId);
        return nullptr;
    }
    AO_DBG_ERR("Could not find connector");
    return nullptr;
}

void MeteringService::beginTxMeterData(Transaction *transaction) {
    if (!transaction) {
        AO_DBG_ERR("invalid argument");
        return;
    }
    auto connectorId = transaction->getConnectorId();
    if (connectorId >= connectors.size()) {
        AO_DBG_ERR("connectorId is out of bounds");
        return;
    }
    auto& connector = connectors[connectorId];

    connector->beginTxMeterData(transaction);
}

std::shared_ptr<TransactionMeterData> MeteringService::endTxMeterData(Transaction *transaction) {
    if (!transaction) {
        AO_DBG_ERR("invalid argument");
        return nullptr;
    }
    auto connectorId = transaction->getConnectorId();
    if (connectorId >= connectors.size()) {
        AO_DBG_ERR("connectorId is out of bounds");
        return nullptr;
    }
    auto& connector = connectors[connectorId];

    return connector->endTxMeterData(transaction);
}

std::shared_ptr<TransactionMeterData> MeteringService::getStopTxMeterData(Transaction *transaction) {
    if (!transaction) {
        AO_DBG_ERR("invalid argument");
        return nullptr;
    }
    auto connectorId = transaction->getConnectorId();
    if (connectorId >= connectors.size()) {
        AO_DBG_ERR("connectorId is out of bounds");
        return nullptr;
    }
    auto& connector = connectors[connectorId];

    return connector->getStopTxMeterData(transaction);
}

bool MeteringService::removeTxMeterData(unsigned int connectorId, unsigned int txNr) {
    return meterStore.remove(connectorId, txNr);
}
